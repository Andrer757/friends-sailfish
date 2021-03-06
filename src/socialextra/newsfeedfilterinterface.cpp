/*
 * Copyright (C) 2013 Lucien XU <sfietkonstantin@free.fr>
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * "Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * The names of its contributors may not be used to endorse or promote
 *     products derived from this software without specific prior written
 *     permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
 */

#include "newsfeedfilterinterface.h"
#include "filterinterface_p.h"
#include "facebook/facebookinterface.h"
#include "socialnetworkmodelinterface.h"
#include "facebook/facebookontology_p.h"
#include "identifiablecontentiteminterface_p.h"
#include "facebookextrapostinterface.h"

// TODO: add place, privacy, source_id,with_location
static const char *QUERY = "{"
    "\"query1\": \"SELECT actor_id, attachment, comment_info, created_time, description,"
                  "description_tags,is_hidden,is_published,like_info,message,message_tags,"
                  "parent_post_id,permalink,place,share_info,"
                  "tagged_ids,target_id,post_id,updated_time,with_tags "
                  "FROM stream "
                  "%1 "
                  "%2 "
                  "ORDER BY created_time DESC LIMIT 30\","
    "\"query2\": \"SELECT uid,name FROM user WHERE uid in (SELECT actor_id,target_id FROM #query1)\","\
    "\"query3\": \"SELECT page_id,name FROM page WHERE page_id in (SELECT actor_id,target_id FROM #query1)\","\
    "\"query4\": \"SELECT gid,name FROM group WHERE gid in (SELECT actor_id,target_id FROM #query1)\","\
    "\"query5\": \"SELECT eid,name FROM event WHERE eid in (SELECT actor_id,target_id FROM #query1)\"}";
static const char *QUERY_QUERY1 = "query1";
static const char *QUERY_QUERY2 = "query2";
static const char *QUERY_QUERY3 = "query3";
static const char *QUERY_QUERY4 = "query4";
static const char *QUERY_QUERY5 = "query5";
static const char *QUERY_DATA_KEY = "data";
static const char *QUERY_NAME_KEY = "name";
static const char *QUERY_RESULT_KEY = "fql_result_set";
static const char *QUERY_UID_KEY = "uid";
static const char *QUERY_PAGEID_KEY = "page_id";
static const char *QUERY_GID_KEY = "gid";
static const char *QUERY_EID_KEY = "eid";

static const char *FIRST_TIMESTAMP_KEY = "first_timestamp";
static const char *LAST_TIMESTAMP_KEY = "last_timestamp";

static const char *WALL_WHERE = "WHERE filter_key in "
                                "    (SELECT filter_key FROM stream_filter "
                                "     WHERE uid = me() AND type = 'newsfeed') ";
static const char *FEED_WHERE = "WHERE source_id = \\\"%1\\\"";


class NewsFeedFilterInterfacePrivate: public FilterInterfacePrivate
{
public:
    explicit NewsFeedFilterInterfacePrivate(NewsFeedFilterInterface *q);
    NewsFeedFilterInterface::FeedType type;
    QString identifier;
};

NewsFeedFilterInterfacePrivate::NewsFeedFilterInterfacePrivate(NewsFeedFilterInterface *q)
    : FilterInterfacePrivate(q), type(NewsFeedFilterInterface::Feed)
{
}

NewsFeedFilterInterface::NewsFeedFilterInterface(QObject *parent) :
    FilterInterface(*(new NewsFeedFilterInterfacePrivate(this)), parent)
{
}

NewsFeedFilterInterface::FeedType NewsFeedFilterInterface::type() const
{
    Q_D(const NewsFeedFilterInterface);
    return d->type;
}

void NewsFeedFilterInterface::setType(FeedType type)
{
    Q_D(NewsFeedFilterInterface);
    if (d->type != type) {
        d->type = type;
        emit typeChanged();
    }
}

QString NewsFeedFilterInterface::identifier() const
{
    Q_D(const NewsFeedFilterInterface);
    return d->identifier;
}

void NewsFeedFilterInterface::setIdentifier(const QString &identifier)
{
    Q_D(NewsFeedFilterInterface);
    if (d->identifier != identifier) {
        d->identifier = identifier;
        emit identifierChanged();
    }
}

bool NewsFeedFilterInterface::isAcceptable(QObject *item, SocialNetworkInterface *socialNetwork) const
{
    if (!testType<FacebookInterface>(socialNetwork)) {
        return false;
    }

    // This filter only works for models
    if (!testType<SocialNetworkModelInterface>(item)) {
        return false;
    }

    return true;
}

bool NewsFeedFilterInterface::performLoadRequestImpl(QObject *item,
                                                     SocialNetworkInterface *socialNetwork,
                                                     LoadType loadType)
{
    Q_D(NewsFeedFilterInterface);
    FacebookInterface *facebook = qobject_cast<FacebookInterface *>(socialNetwork);
    if (!facebook) {
        return false;
    }

    SocialNetworkModelInterface *model = qobject_cast<SocialNetworkModelInterface *>(item);
    if (!model) {
        return false;
    }

    QString where;
    switch(d->type) {
    case Home:
        where = WALL_WHERE;
        break;
    case Feed:
        where = QString(FEED_WHERE).arg(d->identifier);
        break;
    }


    QMap<QString, QString> arguments;
    QString query (QUERY);
    QString firstTimestamp = model->extraData().value(FIRST_TIMESTAMP_KEY).toString();
    QString lastTimestamp = model->extraData().value(LAST_TIMESTAMP_KEY).toString();

    switch (loadType) {
    case FilterInterface::Load:
        query = query.arg(where, QString());
        break;
    case FilterInterface::LoadNext:
        query = query.arg(where, QString("AND created_time < %1").arg(lastTimestamp));
        break;
    case FilterInterface::LoadPrevious:
        query = query.arg(where, QString("AND created_time > %1").arg(firstTimestamp));
        break;
    }
    arguments.insert("q", query);

    QObject *handle = facebook->get(this, QLatin1String("fql"), QString(), QString(), arguments);
    return d->addHandle(handle, item, socialNetwork, loadType);
}

bool NewsFeedFilterInterface::performSetModelDataImpl(SocialNetworkModelInterface *model,
                                                      SocialNetworkInterface *socialNetwork,
                                                      const QByteArray &data, LoadType loadType,
                                                      const QVariantMap &properties)
{
    Q_UNUSED(properties)
    bool ok = false;
    QVariantMap parsed = IdentifiableContentItemInterfacePrivate::parseReplyData(data, &ok);
    if (!ok || !parsed.contains(QUERY_DATA_KEY)) {
        QString errorMessage = QString(QLatin1String("Unable to parse downloaded data. "\
                                                     "Downloaded data: %1")).arg(QString(data));
        model->setError(SocialNetworkInterface::DataError, errorMessage);
        return false;
    }

    QVariantList dataList = parsed.value(QLatin1String("data")).toList();

    // The FQL query will return 5 query results
    // The 1st one contains information about QUERY_SIZE entries of an user's home feed
    // The 4 others contains name that relates to the actor_id (the id of the entity
    // who post the posts listed in the 1st query)

    // We should create a hashmap from the last 4 queries to make retrieving of
    // name easier.
    QMap<QString, QString> names;
    QVariantList mainData;
    foreach (const QVariant entry, dataList) {
        QVariantMap entryObject = entry.toMap();
        QString name = entryObject.value(QUERY_NAME_KEY).toString();
        if (name == QUERY_QUERY1) {
            mainData = entryObject.value(QUERY_RESULT_KEY).toList();
        } else if (name == QUERY_QUERY2
                   || name == QUERY_QUERY3
                   || name == QUERY_QUERY4
                   || name == QUERY_QUERY5) {
            QString key;
            if (name == QUERY_QUERY2) {
                key = QUERY_UID_KEY;
            } else if(name == QUERY_QUERY3) {
                key = QUERY_PAGEID_KEY;
            } else if(name == QUERY_QUERY4) {
                key = QUERY_GID_KEY;
            } else if(name == QUERY_QUERY5) {
                key = QUERY_EID_KEY;
            }

            QVariantList nameList = entryObject.value(QUERY_RESULT_KEY).toList();
            foreach (const QVariant &name, nameList) {
                QVariantMap nameObject = name.toMap();
                names.insert(nameObject.value(key).toString(),
                             nameObject.value(QUERY_NAME_KEY).toString());
            }
        }
    }

    // We will use an algorithm to filter out bad stories (stories without
    // meaningful (meta)data)
    //
    // The idea is the following. If the user posted a message with a given
    // post, like if he/she wrote a status, or if he/she wrote a description of
    // a picture, then the post is a good one.
    // If it is Facebook that is in charge of writing a story, we have to be
    // more cautious. The meaningless stories do not come with data, like
    // a shared link with a caption / description, so we filter out those stories.
    //
    // We also need to be careful about that "parent_post_id" property, because
    // some posts now seems to contain other posts like: "Someone commented on this comment",
    // containing the comment "Another one commented this post", containing the post etc.
    // For this reason, we only take posts whose "parent_post_id" is not the id of
    // another post we're caching.

    // First, build up a list of "primary" posts, and a list of their ids.
    QList<QString> postObjectIds;
    QList<QVariantMap> postObjects;
    foreach (QVariant entry, mainData) {
        QVariantMap post = entry.toMap();
        QString postId = post.value("post_id").toString();
        if (!postObjectIds.contains(postId)) {
            postObjectIds.append(postId);
            postObjects.append(post);
        }
    }

    QList<ContentItemInterface *> modelData;

    uint firstTimestamp = 0;
    uint lastTimestamp = 0;
    if (!postObjects.isEmpty()) {
        QVariantMap first = postObjects.first();
        firstTimestamp = first.value("created_time").toUInt();

        QVariantMap last = postObjects.last();
        lastTimestamp = last.value("created_time").toUInt();
    }

    foreach (const QVariantMap &postMap, postObjects) {
        // Any post with a parent_post_id will be discarded if that parent_post_id
        // is contained in the list of primary post ids, as we already cache that parent post,
        // unless the post_id is the same as the parent_post_id.
        QString postId = postMap.value("post_id").toString();
        QString parentPostId = postMap.value("parent_post_id").toString();
        if (!parentPostId.isEmpty() && postObjectIds.contains(parentPostId) && parentPostId != postId) {
            continue;
        }

        // Discarding not published posts
        if (!postMap.value("is_published").toBool()) {
            continue;
        }

        // Discarding posts without a story and without a message
        QString message = postMap.value(FACEBOOK_ONTOLOGY_POST_MESSAGE).toString();
        QString story = postMap.value("description").toString();
        if (message.isEmpty() && story.isEmpty()) {
            continue;
        }

        // Prepare attachment management:
        QVariantMap attachment = postMap.value("attachment").toMap();

        bool hasMedia = false;
        bool isVideo = false;

        QStringList mediaList;
        // If media was provided, we need to ensure that it's valid, else discard the post.
        if (attachment.keys().contains("media") && !attachment.value("media").isNull()) {
            hasMedia = true;
            bool wrongMediaFound = false;
            QVariantList media = attachment.value("media").toList();

            foreach (QVariant medium, media) {
                QVariantMap mediumMap = medium.toMap();
                if (mediumMap.contains("video")) {
                    isVideo = true;
                }
                QString mediaUrlString = mediumMap.value("src").toString();

                // Skip those media that do not have URL (someone went to an event)
                if (mediaUrlString.isEmpty()) {
                    wrongMediaFound = true;
                    continue;
                }

                // Try to find a better image for this media
                if (mediumMap.contains("photo")) {
                    QVariantList imageList = mediumMap.value("photo").toMap().value("images").toList();
                    if (!imageList.isEmpty()) {
                        QString newImage = imageList.last().toMap().value("src").toString();
                        if (!newImage.isEmpty()) {
                            mediaUrlString = newImage;
                        }
                    }
                }

                // Patch an issue with some applications using local path instead
                // of absolute urls
                if (!mediaUrlString.startsWith("http")) {
                    mediaUrlString.prepend("https://facebook.com/");
                }

                QString urlString = QUrl::fromEncoded(mediaUrlString.toLocal8Bit()).toString();
                mediaList.append(urlString);
            }

            if (wrongMediaFound) {
                continue;
            }
        }

        // Discard stories without attachments
        if (!hasMedia && !story.isEmpty() && message.isEmpty()) {
            continue;
        }

        QUrl source;
        if (attachment.contains("href")) {
            source = attachment.value("href").toUrl();
        }

        QVariantMap postData;
        postData.insert(NEMOQMLPLUGINS_SOCIAL_CONTENTITEMID, postId);
        postData.insert(FACEBOOK_ONTOLOGY_METADATA_ID, postId);

        // Extra fields
        postData.insert(MEDIA_KEY, mediaList);
        postData.insert(IS_VIDEO_KEY, isVideo);

        // From
        QString fromId = postMap.value("actor_id").toString();
        QString fromName = names.value(fromId);
        QVariantMap fromData;
        fromData.insert(FACEBOOK_ONTOLOGY_OBJECTREFERENCE_OBJECTIDENTIFIER, fromId);
        fromData.insert(FACEBOOK_ONTOLOGY_OBJECTREFERENCE_OBJECTNAME, fromName);
        postData.insert(FACEBOOK_ONTOLOGY_POST_FROM, fromData);

        // To
        // If to is not null, there is usually one target ...
        QString toId = postMap.value("target_id").toString();
        if (!toId.isEmpty()) {
            QString toName = names.value(toId);
            QVariantMap toData;
            toData.insert(FACEBOOK_ONTOLOGY_OBJECTREFERENCE_OBJECTIDENTIFIER, toId);
            toData.insert(FACEBOOK_ONTOLOGY_OBJECTREFERENCE_OBJECTNAME, toName);
            QVariantMap toDataWrapper;
            QVariantList toDataList;
            toDataList.append(toData);
            toDataWrapper.insert(FACEBOOK_ONTOLOGY_METADATA_DATA, toDataList);
            postData.insert(FACEBOOK_ONTOLOGY_POST_TO, toDataWrapper);
        }

        // Message
        postData.insert(FACEBOOK_ONTOLOGY_POST_MESSAGE, message);
        // Message tags
        postData.insert(FACEBOOK_ONTOLOGY_POST_MESSAGETAGS,
                        postMap.value(FACEBOOK_ONTOLOGY_POST_MESSAGETAGS));

        // Picture is not used so we don't set it.
        QString attachmentName = attachment.value("name").toString();
        QString attachmentCaption = attachment.value("caption").toString();
        QString attachmentDescription = attachment.value("description").toString();
        QString attachmentUrl = attachment.value("href").toString();

        // Facebook object id / type
        postData.insert(FACEBOOK_OBJECT_ID, attachment.value(FACEBOOK_OBJECT_ID));
        postData.insert(FACEBOOK_OBJECT_TYPE, attachment.value(FACEBOOK_OBJECT_TYPE));

        // Link
        postData.insert(FACEBOOK_ONTOLOGY_POST_LINK, attachmentUrl);

        // Name
        postData.insert(FACEBOOK_ONTOLOGY_POST_NAME, attachmentName);

        // Caption
        postData.insert(FACEBOOK_ONTOLOGY_POST_CAPTION, attachmentCaption);

        // Story
        postData.insert(FACEBOOK_ONTOLOGY_POST_DESCRIPTION, attachmentDescription);

        // Source
        if (!source.host().contains("facebook.com")) {
            postData.insert(FACEBOOK_ONTOLOGY_POST_SOURCE, source);
        } else {
            postData.insert(FACEBOOK_ONTOLOGY_POST_SOURCE, QUrl());
        }
        // Source is not used so we don't set it.
        // Properties is not used so we don't set it.
        // Icon is not used so we don't set it.
        // Actions is not queried (TODO ?)
        // postType is not reliable
        // Story
        postData.insert(FACEBOOK_ONTOLOGY_POST_STORY, story);

        // Story tags
        postData.insert(FACEBOOK_ONTOLOGY_POST_STORYTAGS, postMap.value("description_tags"));

        // With tags
        postData.insert(FACEBOOK_ONTOLOGY_POST_WITHTAGS,
                        postMap.value(FACEBOOK_ONTOLOGY_POST_WITHTAGS));

        // ObjectIdentifier is not used so we don't set it.
        // Application is not queried (TODO ?)
        // CreatedTime
        uint createdTimestamp = postMap.value(FACEBOOK_ONTOLOGY_POST_CREATEDTIME).toUInt();
        QDateTime createdTime = QDateTime::fromTime_t(createdTimestamp);
        createdTime = createdTime.toTimeSpec(Qt::UTC);
        postData.insert(FACEBOOK_ONTOLOGY_POST_CREATEDTIME, createdTime.toString(Qt::ISODate));

        // UpdatedTime
        uint updatedTimestamp = postMap.value(FACEBOOK_ONTOLOGY_POST_UPDATEDTIME).toUInt();
        QDateTime updatedTime = QDateTime::fromTime_t(updatedTimestamp);
        updatedTime = updatedTime.toTimeSpec(Qt::UTC);
        postData.insert(FACEBOOK_ONTOLOGY_POST_UPDATEDTIME, updatedTime.toString(Qt::ISODate));

        // Shares
        QVariantMap shareInfo = postMap.value("share_info").toMap();
        int shares = shareInfo.value("share_count").toInt();
        postData.insert(FACEBOOK_ONTOLOGY_POST_SHARES, shares);

        // Hidden
        bool hidden = postMap.value(FACEBOOK_ONTOLOGY_POST_HIDDEN).toBool();
        postData.insert(FACEBOOK_ONTOLOGY_POST_HIDDEN, hidden ? "true" : "false");

        // StatusType is not used so we don't set it.
        QString currentUserIdentifier = qobject_cast<FacebookInterface*>(socialNetwork)->currentUserIdentifier();
        // Likes
        QVariantMap likeInfo = postMap.value("like_info").toMap();
        QVariantMap likesData;
        QVariantMap likesSummary;
        likesSummary.insert(FACEBOOK_ONTOLOGY_METADATA_TOTALCOUNT,
                            likeInfo.value("like_count").toInt());
        QVariantList likesObjects;
        if (likeInfo.value("user_likes").toBool()) {
            QVariantMap me;
            me.insert(FACEBOOK_ONTOLOGY_OBJECTREFERENCE_OBJECTIDENTIFIER, currentUserIdentifier);
            likesObjects.append(me);
        }
        likesData.insert(FACEBOOK_ONTOLOGY_METADATA_SUMMARY, likesSummary);
        likesData.insert(FACEBOOK_ONTOLOGY_METADATA_DATA, likesObjects);
        postData.insert(FACEBOOK_ONTOLOGY_CONNECTIONS_LIKES, likesData);
        // Comments
        QVariantMap commentInfo = postMap.value("comment_info").toMap();
        QVariantMap commentData;
        QVariantMap commentSummary;
        commentSummary.insert(FACEBOOK_ONTOLOGY_METADATA_TOTALCOUNT,
                            commentInfo.value("comment_count").toInt());
        commentData.insert(FACEBOOK_ONTOLOGY_METADATA_SUMMARY, commentSummary);
        postData.insert(FACEBOOK_ONTOLOGY_CONNECTIONS_COMMENTS, commentData);

        FacebookExtraPostInterface *post = new FacebookExtraPostInterface(model);
        post->setSocialNetwork(socialNetwork);
        post->setData(postData);
        post->classBegin();
        post->componentComplete();
        modelData.append(post);
    }


    // We update only the correct timestamps
    uint oldFirstTimestamp = model->extraData().value(FIRST_TIMESTAMP_KEY, 0).toUInt();
    uint oldLastTimestamp = model->extraData().value(LAST_TIMESTAMP_KEY, 0).toUInt();

    // Populate model depending on the type of load
    switch (loadType) {
    case FilterInterface::Load:
        model->setModelData(modelData);
        break;
    case FilterInterface::LoadPrevious:
        if (firstTimestamp == 0) {
            firstTimestamp = oldFirstTimestamp;
        }
        lastTimestamp = oldLastTimestamp;
        model->prependModelData(modelData);
        break;
    case FilterInterface::LoadNext:
        if (lastTimestamp == 0) {
            lastTimestamp = oldLastTimestamp;
        }
        firstTimestamp = oldFirstTimestamp;
        model->appendModelData(modelData);
        break;
    default:
        break;
    }

    QVariantMap extraData;
    extraData.insert(FIRST_TIMESTAMP_KEY, firstTimestamp);
    extraData.insert(LAST_TIMESTAMP_KEY, lastTimestamp);


    model->setPagination(true, lastTimestamp > 0 ? true : false);
    model->setExtraData(extraData);

    return true;
}
